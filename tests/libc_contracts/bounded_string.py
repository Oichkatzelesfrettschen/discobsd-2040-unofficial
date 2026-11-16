"""Execute and calibrate bounded-string mechanisms with renamed libc sources."""

import argparse
import os
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

NAMES = ("strnlen", "strndup", "stpcpy", "stpncpy")
FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
         "-fno-builtin", "-D_POSIX_C_SOURCE=200809L"]
RENAMES = ["-D" + name + "=db_" + name for name in NAMES] + ["-Dmalloc=bounded_alloc"]
HEADERS = """#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
"""
ARM = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os"]
OVERFLOW = "\tif (len == SIZE_MAX) {\n\t\terrno = ENOMEM;\n\t\treturn (NULL);\n\t}\n"


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def compile_command(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=90,
                            check=False)
    require(result.returncode == 0 and not result.stderr,
            "bounded string compile failure: " + result.stdout + result.stderr)


def namespace_header(directory):
    header = directory / "namespace.h"
    # System headers may replace command-line renames with fortified macros.
    text = HEADERS
    for name, replacement in [(name, "db_" + name) for name in NAMES] + [
            ("malloc", "bounded_alloc")]:
        text += f"#undef {name}\n#define {name} {replacement}\n"
    text += """size_t strnlen(const char *, size_t);
char *strndup(const char *, size_t);
char *stpcpy(char * restrict, const char * restrict);
char *stpncpy(char * restrict, const char * restrict, size_t);
void *malloc(size_t);
"""
    header.write_text(text)
    return header


def build(root, compiler, directory, target, mutant=None, overflow=False, macros=None):
    library = root / "lib/libc/gen"
    flags = FLAGS + (ARM if target else [])
    includes = (["-include", str(macros)] if macros else []) + [
        "-include", str(namespace_header(directory))]
    objects = []
    for name in (("strndup",) if overflow else NAMES):
        source = mutant[1] if mutant and name == mutant[0] else library / (name + ".c")
        unit = directory / (name + ".o")
        compile_command(compiler + flags + includes + ["-c", str(source), "-o", str(unit)])
        objects.append(str(unit))
    fixture = root / "tests/libc_contracts" / (
        "bounded_string_overflow_test.c" if overflow else "bounded_string_test.c")
    unit = directory / "fixture.o"
    compile_command(compiler + flags + (["-DBOUNDED_STRING_NO_GUARD"] if target else []) +
                    ["-c", str(fixture), "-o", str(unit)])
    objects.append(str(unit))
    link = []
    if target:
        support = directory / "support.o"
        compile_command(compiler + ARM + ["-std=gnu17", "-Wall", "-Wextra", "-Werror",
                        "-fomit-frame-pointer", "-c", str(root / "tests/libbn/qemu_support.c"),
                        "-o", str(support)])
        objects.append(str(support))
        link = ["-nostartfiles", "-Wl,-Ttext=0x20000", "-lc", "-lgcc"]
    program = directory / "bounded_string"
    compile_command(compiler + (ARM if target else []) + objects + link +
                    ["-o", str(program)])
    return program


def macro_control(root, compiler, directory, target):
    macros = directory / "system_macros.h"
    macros.write_text(HEADERS + "".join(
        f"#undef {name}\n#define {name}(...) bounded_host_macro_replaces_rename +\n"
        for name in NAMES))
    source = root / "lib/libc/gen/stpcpy.c"
    result = subprocess.run(compiler + FLAGS + (ARM if target else []) + RENAMES +
                            ["-include", str(macros), "-c", str(source), "-o",
                             str(directory / "macro_control.o")],
                            capture_output=True, text=True, timeout=90, check=False)
    require(result.returncode != 0 and "bounded_host_macro_replaces_rename" in result.stderr,
            "header macro control lacks a diagnostic for the replaced rename")
    program = build(root, compiler, directory, target, macros=macros)
    result = run(program, ["matrix"], target)
    require(result.returncode == 0 and not result.stderr,
            "post-header renames fail with system macros: " + result.stdout + result.stderr)
    print("bounded strings: header macros reject early renames; post-header renames pass")


def run(program, arguments, target):
    prefix = ["qemu-arm", "-cpu", "cortex-m0"] if target else []
    return subprocess.run(prefix + [str(program)] + arguments, capture_output=True,
                          text=True, timeout=120, check=False,
                          env=dict(os.environ, LC_ALL="C"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("compiler")
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--arm", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    compiler = shlex.split(args.compiler)
    require(bool(compiler), "bounded string compiler is empty")
    if args.arm:
        require(shutil.which("qemu-arm") is not None, "qemu-arm is required")
    mutations = [
        ("scan-bound", "strnlen", "len < maxlen", "len <= maxlen"),
        ("duplicate-terminator", "strndup", "copy[len] = '\\0';", "copy[len] = 'x';"),
        ("duplicate-capacity", "strndup", "malloc(len + 1)", "malloc(len + 2)"),
        ("copy-return", "stpcpy", "return(to);", "return(to + 1);"),
        ("padded-return", "stpncpy", "return (ret);", "(void)ret; return (dst);"),
        ("padding-byte", "stpncpy", "*++dst = '\\0';", "*++dst = 'x';"),
        ("duplicate-overflow", "strndup", OVERFLOW, ""),
    ]
    with tempfile.TemporaryDirectory(prefix="bounded-string-") as temporary:
        directory = Path(temporary)
        program = build(root, compiler, directory, args.arm)
        for case in (["matrix"] if args.arm else ["matrix", "guard"]):
            result = run(program, [case], args.arm)
            require(result.returncode == 0 and not result.stderr,
                    f"bounded string {case} fails: {result.stdout}{result.stderr}")
            print(result.stdout, end="")
        accepted = directory / "accepted"
        shutil.copy2(program, accepted)
        macro_control(root, compiler, directory, args.arm)
        program = build(root, compiler, directory, args.arm, overflow=True)
        result = run(program, [], args.arm)
        require(result.returncode == 0 and not result.stderr,
                "bounded string overflow fails: " + result.stdout + result.stderr)
        print(result.stdout, end="")
        for label, name, before, after in mutations:
            original = (root / "lib/libc/gen" / (name + ".c")).read_text()
            require(original.count(before) == 1, label + " mutation anchor differs")
            mutant = directory / "mutant.c"
            mutant.write_text(original.replace(before, after))
            overflow = label == "duplicate-overflow"
            program = build(root, compiler, directory, args.arm, (name, mutant), overflow)
            result = run(program, [] if overflow else ["matrix"], args.arm)
            require(result.returncode == 1 and "bounded string mismatch:" in result.stderr,
                    label + " lacks behavioral rejection: " + result.stdout + result.stderr)
            print("bounded string mutant rejected: " + label)
        if args.output:
            shutil.copy2(accepted, args.output)
    print("bounded strings: seven behavioral controls reject")


if __name__ == "__main__":
    main()
