"""Execute production byte searches and calibrate result/work/domain witnesses."""

import argparse
import os
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
         "-fno-builtin", "-O2"]
ARM = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os"]


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def compile_command(arguments):
    result = subprocess.run(arguments, capture_output=True, text=True,
                            timeout=90, check=False)
    require(result.returncode == 0 and not result.stderr,
            "byte search compile failure: " + result.stdout + result.stderr)


def build(root, compiler, work, arm, replacement=None, counter=True, bound_control=False):
    namespace = work / "namespace.h"
    namespace.write_text("""#include <stddef.h>
#include <string.h>
#undef memmem
#undef memchr
#undef memcmp
#define memmem candidate_memmem
#define memchr tree_memchr
void *memmem(const void *, size_t, const void *, size_t);
void *memchr(const void *, int, size_t);
extern size_t search_comparisons;
int byte_compare(const void *, const void *, size_t);
void *byte_find(const void *, int, size_t);
""")
    instrumentation = work / "instrumentation.h"
    instrumentation.write_text("""#undef memchr
#define memchr byte_find
#define memcmp byte_compare
#define MEMMEM_ORDER(a,b) (""" + ("search_comparisons++, " if counter else "") +
                               "((a) > (b)) - ((a) < (b)))\n")
    flags = FLAGS + (ARM if arm else [])
    objects = []
    for name, path in [("memmem", "lib/libc/string/memmem.c"),
                       ("memchr", "lib/libc/compat/memchr.c")]:
        source = replacement[1] if replacement and replacement[0] == name else root / path
        obj = work / (name + ".o")
        extra = ["-include", str(instrumentation)] if name == "memmem" else []
        compile_command(compiler + flags + ["-include", str(namespace)] + extra +
                        ["-c", str(source), "-o", str(obj)])
        objects.append(str(obj))
    obj = work / "fixture.o"
    fixture = root / "tests/libc_contracts/byte_search_test.c"
    if bound_control:
        text = fixture.read_text()
        require(text.count("2 * n + 7 * m") == 1, "bound control anchor differs")
        fixture = work / "bound_control.c"
        fixture.write_text(text.replace("2 * n + 7 * m", "0"))
    compile_command(compiler + flags + ([] if arm else ["-DGUARD_PAGES", "-D_GNU_SOURCE",
                                                       "-D_DARWIN_C_SOURCE"]) +
                    ["-c", str(fixture), "-o", str(obj)])
    objects.append(str(obj))
    link = []
    if arm:
        support = work / "support.o"
        compile_command(compiler + ARM + ["-std=gnu17", "-Wall", "-Wextra", "-Werror",
                        "-fomit-frame-pointer", "-c", str(root / "tests/libbn/qemu_support.c"),
                        "-o", str(support)])
        objects.append(str(support))
        link = ["-nostartfiles", "-Wl,-Ttext=0x20000", "-lc", "-lgcc"]
    program = work / "byte_search"
    compile_command(compiler + (ARM if arm else []) + objects + link + ["-o", str(program)])
    return program


def execute(program, arm, case):
    prefix = ["qemu-arm", "-cpu", "cortex-m0"] if arm else []
    return subprocess.run(prefix + [str(program), case], capture_output=True, text=True,
                          timeout=180, check=False, env=dict(os.environ, LC_ALL="C"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("compiler")
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--arm", action="store_true")
    parser.add_argument("--case", choices=["memchr", "memmem"])
    parser.add_argument("--calibrate", action="store_true")
    args = parser.parse_args()
    compiler = shlex.split(args.compiler)
    root = args.root.resolve()
    require(bool(compiler), "byte search compiler is empty")
    with tempfile.TemporaryDirectory(prefix="byte-search-") as temporary:
        work = Path(temporary)
        program = build(root, compiler, work, args.arm)
        cases = [args.case] if args.case else ["memchr", "memmem"]
        for case in cases:
            result = execute(program, args.arm, case)
            marker = (f"memchr: {263170 if args.arm else 263427} conversion and "
                      "extent cases pass\n" if case == "memchr" else
                      f"byte search: {4206113 if args.arm else 4239266} earliest-match "
                      "and comparison-bound cases pass; maximum count 5132\n")
            require(result.returncode == 0 and not result.stderr and
                    result.stdout == marker,
                    "byte search " + case + " fails: " + result.stdout + result.stderr)
            print(result.stdout, end="", flush=True)
        if args.output:
            shutil.copy2(program, args.output)
        if args.calibrate:
            controls(root, compiler, work, args.arm)


def controls(root, compiler, work, arm):
    search = (root / "lib/libc/string/memmem.c").read_text()
    character = (root / "lib/libc/compat/memchr.c").read_text()
    changes = [
        ("left-half", "memmem", search, "if (k <= memory)",
         "if (k <= length)", "memmem", 1, "oracle:"),
        ("later-candidate", "memmem", search, "haystack += shift;",
         "return NULL;", "memmem", 1, "oracle:"),
        ("empty-needle", "memmem", search, "return (void *)haystack_pointer;",
         "return NULL;", "memmem", 1, "oracle:"),
        ("int-conversion", "memchr", character, "unsigned char value = (unsigned char)c;",
         "int value = c;", "memchr", 5, "byte conversion:"),
        ("signed-byte", "memchr", character, "const unsigned char *s = vs;",
         "const signed char *s = vs;", "memchr", 5, "byte conversion:"),
    ]
    for label, name, source, before, after, case, status, diagnostic in changes:
        require(source.count(before) == 1, label + " mutation anchor differs")
        mutant = work / (label + ".c")
        mutant.write_text(source.replace(before, after))
        program = build(root, compiler, work, arm, (name, mutant))
        result = execute(program, arm, case)
        require(result.returncode == status and diagnostic in result.stderr,
                label + " lacks named rejection: " + result.stdout + result.stderr)
        print("byte search mutant rejected: " + label, flush=True)
    program = build(root, compiler, work, arm, counter=False)
    result = execute(program, arm, "memmem")
    require(result.returncode == 3 and "counter:" in result.stderr,
            "disabled counter lacks named rejection: " + result.stdout + result.stderr)
    print("byte search mutant rejected: disabled counter", flush=True)
    program = build(root, compiler, work, arm, bound_control=True)
    result = execute(program, arm, "memmem")
    require(result.returncode == 2 and "comparison bound:" in result.stderr,
            "zero work budget lacks named rejection: " + result.stdout + result.stderr)
    print("byte search control rejected: zero work budget", flush=True)


if __name__ == "__main__":
    main()
